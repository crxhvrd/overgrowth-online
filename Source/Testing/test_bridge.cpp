//-----------------------------------------------------------------------------
//           Name: test_bridge.cpp
//      Developer: Overgrowth Online (crxhvrd)
//    Description: See test_bridge.h. Protocol, one JSON object per line:
//                 request  {"id": 7, "cmd": "state", ...arguments}
//                 response {"id": 7, "ok": true, ...data}
//                       or {"id": 7, "ok": false, "error": "..."}
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#include "test_bridge.h"

#include <Main/engine.h>
#include <Main/scenegraph.h>
#include <Game/level.h>
#include <Objects/movementobject.h>
#include <Objects/riggedobject.h>
#include <Objects/itemobject.h>
#include <Online/char_snapshot.h>
#include <Online/online.h>
#if ENABLE_STEAMWORKS
#include <Steam/steamworks.h>
#include <Steam/steamworks_matchmaking.h>
#endif
#include <Online/clock_sync.h>
#include <Editors/map_editor.h>
#include <Scripting/scriptparams.h>
#include <Internal/config.h>
#include <UserInput/input.h>
#include <Graphics/graphics.h>
#include <Graphics/camera.h>
#include <Graphics/frame_timers.h>
#include <Internal/timer.h>
#include <Logging/logdata.h>
#include <JSON/json.h>

#include <SDL_net.h>

#include <chrono>
#include <cmath>
#include <exception>
#include <stdexcept>

extern Timer game_timer;
extern Config config;

namespace {

double EpochMs() {
    using namespace std::chrono;
    return (double)duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

Json::Value Vec(const vec3& v) {
    Json::Value a(Json::arrayValue);
    a.append(v[0]);
    a.append(v[1]);
    a.append(v[2]);
    return a;
}

MovementObject* FindCharacter(SceneGraph* sg, int id) {
    if (sg == nullptr) {
        return nullptr;
    }

    for (Object* obj : sg->movement_objects_) {
        if (obj->GetID() == id) {
            return (MovementObject*)obj;
        }
    }

    return nullptr;
}

// This machine's own character: the session's own player online (other
// players' characters are controlled too there), else the first avatar
int MyAvatar(Engine* engine) {
    Online* online = Online::Instance();

    if (online->IsActive()) {
        return (int)online->GetOwnPlayerState().object_id;
    }

    return engine->num_avatars > 0 ? engine->avatar_ids[0] : -1;
}

}  // namespace

TestBridge* TestBridge::Instance() {
    static TestBridge bridge;
    return &bridge;
}

bool TestBridge::Start(int port) {
    if (running_ || port <= 0 || port > 65535) {
        return false;
    }

    port_ = port;
    running_ = true;
    thread_ = std::thread(&TestBridge::ThreadMain, this);
    LOGI << "Test bridge listening on port " << port << std::endl;
    return true;
}

void TestBridge::Stop() {
    if (!running_) {
        return;
    }

    running_ = false;

    if (thread_.joinable()) {
        thread_.join();
    }
}

void TestBridge::ThreadMain() {
    IPaddress addr;

    if (SDLNet_ResolveHost(&addr, NULL, (Uint16)port_) != 0) {
        LOGE << "Test bridge: cannot resolve the listen address" << std::endl;
        running_ = false;
        return;
    }

    TCPsocket listen = SDLNet_TCP_Open(&addr);

    if (listen == NULL) {
        LOGE << "Test bridge: cannot listen on port " << port_ << ": " << SDLNet_GetError() << std::endl;
        running_ = false;
        return;
    }

    SDLNet_SocketSet set = SDLNet_AllocSocketSet(2);
    SDLNet_TCP_AddSocket(set, listen);
    TCPsocket client = NULL;
    std::string buffer;

    while (running_) {
        // Blocks up to 10 ms: no busy spin
        int ready = SDLNet_CheckSockets(set, 10);

        if (ready > 0 && SDLNet_SocketReady(listen)) {
            TCPsocket s = SDLNet_TCP_Accept(listen);

            if (s != NULL) {
                // SDL_net always listens on every interface: refuse anything but loopback
                IPaddress* peer = SDLNet_TCP_GetPeerAddress(s);
                bool loopback = peer != NULL && (SDLNet_Read32(&peer->host) >> 24) == 127;

                if (!loopback || client != NULL) {
                    SDLNet_TCP_Close(s);
                } else {
                    client = s;
                    SDLNet_TCP_AddSocket(set, client);
                    buffer.clear();
                }
            }
        }

        if (client != NULL && ready > 0 && SDLNet_SocketReady(client)) {
            char chunk[4096];
            int n = SDLNet_TCP_Recv(client, chunk, sizeof(chunk));

            if (n <= 0) {
                SDLNet_TCP_DelSocket(set, client);
                SDLNet_TCP_Close(client);
                client = NULL;
            } else {
                buffer.append(chunk, n);
                size_t pos;

                while ((pos = buffer.find('\n')) != std::string::npos) {
                    std::lock_guard<std::mutex> guard(lock_);
                    incoming_.push_back(buffer.substr(0, pos));
                    buffer.erase(0, pos + 1);
                }
            }
        }

        std::deque<std::string> out;
        {
            std::lock_guard<std::mutex> guard(lock_);
            out.swap(outgoing_);
        }

        for (const std::string& line : out) {
            if (client == NULL) {
                break;
            }

            if (SDLNet_TCP_Send(client, line.data(), (int)line.size()) < (int)line.size()) {
                SDLNet_TCP_DelSocket(set, client);
                SDLNet_TCP_Close(client);
                client = NULL;
            }
        }
    }

    if (client != NULL) {
        SDLNet_TCP_Close(client);
    }

    SDLNet_TCP_Close(listen);
    SDLNet_FreeSocketSet(set);
}

void TestBridge::NoteLevelMessage(const std::string& msg) {
    if (!running_ || msg.compare(0, 8, "tutorial") == 0) {
        return;  // (the stock scripts send "tutorial ..." many times a second)
    }
    level_messages_.push_back(msg);
    while (level_messages_.size() > 400) {
        level_messages_.pop_front();
    }
}

void TestBridge::Pump(Engine* engine) {
    if (!running_) {
        return;
    }

    ++frame_;
    std::deque<std::string> in;
    {
        std::lock_guard<std::mutex> guard(lock_);
        in.swap(incoming_);
    }

    for (const std::string& line : in) {
        std::string response = Handle(line, engine);
        std::lock_guard<std::mutex> guard(lock_);
        outgoing_.push_back(response);
    }
}

std::string TestBridge::Handle(const std::string& line, Engine* engine) {
    Json::Value req;
    Json::Value out(Json::objectValue);
    Json::Reader reader;

    if (!reader.parse(line, req) || !req.isObject()) {
        out["ok"] = false;
        out["error"] = "bad json";
    } else {
        out["id"] = req.get("id", Json::Value());
        std::string cmd = req.get("cmd", "").asString();

        try {
            out["ok"] = true;
            Dispatch(cmd, req, out, engine);
        } catch (const std::exception& e) {
            out["ok"] = false;
            out["error"] = e.what();
        }
    }

    Json::FastWriter writer;
    return writer.write(out);  // ends with '\n'
}

void TestBridge::Dispatch(const std::string& cmd, const Json::Value& req, Json::Value& out, Engine* engine) {
    SceneGraph* sg = engine->GetSceneGraph();
    bool loaded = engine->IsLevelLoaded() && sg != nullptr;
    Online* online = Online::Instance();

    if (cmd == "ping") {
        out["frame"] = (Json::UInt64)frame_;
        out["epoch_ms"] = EpochMs();
    } else if (cmd == "status") {
        out["frame"] = (Json::UInt64)frame_;
        out["epoch_ms"] = EpochMs();
        out["game_time"] = game_timer.game_time;
        out["clock_ms"] = OnlineClockMs();  // this instance's online clock (clock sync checks)
        out["camera_pos"] = Vec(ActiveCameras::Get()->GetPos());  // the view on screen (camera jitter checks)
        out["level_loaded"] = loaded;
        out["waiting_for_input"] = engine->waiting_for_input_;
        out["level"] = loaded ? sg->level_path_.GetOriginalPath() : "";
        out["my_avatar"] = loaded ? MyAvatar(engine) : -1;
        out["gl_problems"] = GLDebugProblemCount();  // GL debug output: errors, undefined, deprecated ("gl_debug")
        Json::Value& o = out["online"];
        o["active"] = online->IsActive();
        o["hosting"] = online->IsHosting();
        o["client"] = online->IsClient();
        o["bytes_out"] = (Json::UInt64)online->net_bytes_out.load();
        o["bytes_in"] = (Json::UInt64)online->net_bytes_in.load();
        o["msgs_out"] = (Json::UInt64)online->net_msgs_out.load();
        o["msgs_in"] = (Json::UInt64)online->net_msgs_in.load();
        o["dropped"] = (Json::UInt64)online->net_sim_dropped.load();
        o["records_out"] = (Json::UInt64)online->snapshot_records_out.load();
        o["records_in"] = (Json::UInt64)online->snapshot_records_in.load();
        o["snapshot_bytes_out"] = (Json::UInt64)online->snapshot_bytes_out.load();
        o["idle_skips"] = (Json::UInt64)online->snapshot_idle_skips.load();
#if ENABLE_STEAMWORKS
        {
            SteamworksMatchmaking* matchmaking = Steamworks::Instance()->GetMatchmaking();
            o["steam_lobby"] = matchmaking != nullptr ? std::to_string(matchmaking->CurrentLobby()) : "0";
            o["steam"] = Steamworks::Instance()->IsConnected();
        }
#endif
        if (online->online_session != nullptr) {  // (stress tests: the queues must not grow)
            std::lock_guard<std::mutex> guard(online->online_session->outgoing_messages_lock);
            o["out_queue"] = (int)online->online_session->outgoing_messages.size();
            o["persistent_messages"] = (int)online->online_session->persistent_outgoing_messages.size();
        }
        o["bones_captured"] = (Json::UInt64)online->snapshot_bones_captured.load();
        o["hits_requested"] = (Json::UInt64)online->hits_requested.load();
        o["hits_resolved"] = (Json::UInt64)online->hits_resolved.load();
        o["hit_results"] = (Json::UInt64)online->hit_results.load();
        o["takeovers"] = (Json::UInt64)online->takeovers.load();
        o["item_launches"] = (Json::UInt64)online->item_launches.load();
        o["item_hits"] = (Json::UInt64)online->item_hits.load();
        o["sounds_from_clients"] = (Json::UInt64)online->sounds_from_clients.load();
        o["overrides"] = (int)online->owner_overrides.size();
        o["positions_sent"] = (Json::UInt64)online->snapshot_positions_sent.load();
        o["sim_latency_ms"] = online->net_sim_latency_ms.load();
        o["sim_jitter_ms"] = online->net_sim_jitter_ms.load();
        o["sim_loss"] = online->net_sim_loss.load();
        o["peers"] = Json::Value(Json::arrayValue);
        o["players"] = Json::Value(Json::arrayValue);

        if (online->IsActive()) {
            for (const Peer& peer : online->GetPeers()) {
                Json::Value p;
                p["peer_id"] = (Json::UInt64)peer.peer_id;
                p["ping_ms"] = peer.current_ping_delta * 1000.0f;
                p["transport_ping_ms"] = online->GetPeerTransportPing(peer.conn_id);
                p["clock_offset_ms"] = peer.clock.Offset();
                p["clock_samples"] = peer.clock.Samples();
                o["peers"].append(p);
            }

            for (const PlayerState& player : online->GetPlayerStates()) {
                Json::Value p;
                p["name"] = player.playername;
                p["avatar_id"] = player.object_id;
                o["players"].append(p);
            }
        }
    } else if (cmd == "state") {
        out["characters"] = Json::Value(Json::arrayValue);
        out["my_avatar"] = loaded ? MyAvatar(engine) : -1;

        if (loaded) {
            for (Object* obj : sg->movement_objects_) {
                MovementObject* mo = (MovementObject*)obj;
                Json::Value c;
                c["id"] = mo->GetID();
                c["common_id"] = online->GetOriginalID(mo->GetID());  // the host's id: the same on every machine
                c["controlled"] = mo->controlled;
                c["remote"] = mo->remote;
                c["is_player"] = mo->is_player;
                c["teams"] = mo->GetScriptParams()->HasParam("Teams") ? mo->GetScriptParams()->GetStringVal("Teams") : "";
                c["controller_id"] = mo->controller_id;
                c["camera_id"] = mo->camera_id;
                c["pos"] = Vec(mo->position);
                c["visible"] = mo->visible;
                c["vel"] = Vec(mo->velocity);
                c["proxy"] = mo->network_proxy;
                c["bones"] = (int)mo->rigged_object()->skeleton_.physics_bones.size();
                c["morphs"] = (int)mo->rigged_object()->morph_targets.size();
                c["char_path"] = mo->character_path;
                c["palette"] = Json::Value(Json::arrayValue);
                for (const LabeledColor& lc : *mo->GetPalette()) {
                    Json::Value e(Json::arrayValue);
                    e.append(lc.label);
                    e.append(lc.color[0]);
                    e.append(lc.color[1]);
                    e.append(lc.color[2]);
                    c["palette"].append(e);
                }

                if (mo->network_proxy) {
                    c["net_delay_ms"] = mo->net_buffer.DelayMs();
                    c["net_interval_ms"] = mo->net_buffer.IntervalMs();
                    c["net_frames"] = mo->net_buffer.Size();
                }

                if (mo->ASHasVar("knocked_out")) {
                    c["knocked_out"] = mo->ASGetIntVar("knocked_out");
                }

                if (mo->ASHasVar("state")) {
                    c["state"] = mo->ASGetIntVar("state");
                }

                if (mo->ASHasVar("on_ground")) {
                    c["on_ground"] = mo->ASGetBoolVar("on_ground");
                }

                if (mo->ASHasVar("temp_health")) {
                    c["temp_health"] = mo->ASGetFloatVar("temp_health");
                }

                if (mo->ASHasVar("blood_health")) {
                    c["blood_health"] = mo->ASGetFloatVar("blood_health");
                }

                out["characters"].append(c);
            }

            out["items"] = Json::Value(Json::arrayValue);
            for (Object* obj : sg->objects_) {
                if (obj->GetType() != _item_object) {
                    continue;
                }
                ItemObject* io = static_cast<ItemObject*>(obj);
                Json::Value it;
                it["id"] = io->GetID();
                it["common_id"] = online->GetOriginalID(io->GetID());
                it["label"] = io->item_ref()->GetLabel();
                it["held_by"] = io->HeldByWhom();
                it["hidden"] = io->HolderHidden();  // not drawn: on a hidden character
                it["file"] = io->obj_file;
                it["stuck_in"] = io->StuckInWhom();
                it["pos"] = Vec(io->GetPhysicsTransform().GetTranslationPart());
                it["draw_pos"] = Vec(io->GetTransform().GetTranslationPart());  // where it is drawn
                out["items"].append(it);
            }
        }
    } else if (cmd == "exec") {
        if (!loaded) {
            throw std::runtime_error("no level loaded");
        }

        std::string code = req.get("code", "").asString();
        const Json::Value& target = req["target"];

        if (target.isString() && target.asString() == "level") {
            sg->level->Execute(code);
        } else if (target.isInt()) {
            MovementObject* mo = FindCharacter(sg, target.asInt());

            if (mo == nullptr) {
                throw std::runtime_error("no such character");
            }

            mo->Execute(code);
        } else {
            throw std::runtime_error("target must be \"level\" or a character id");
        }
    } else if (cmd == "input") {
        // Keys for the local player (controller 0), applied every 120 Hz step
        // after the real devices (Input::ApplyInjectedInput)
        Input* input = Input::Instance();

        if (req.get("release_all", false).asBool()) {
            input->injected_keys_.clear();
        }

        const Json::Value& release = req["release"];

        if (release.isArray()) {
            for (const Json::Value& name : release) {
                input->injected_keys_.erase(name.asString());
            }
        }

        const Json::Value& hold = req["hold"];
        int steps = req.get("steps", -1).asInt();  // -1: until released

        if (hold.isObject()) {
            for (const std::string& name : hold.getMemberNames()) {
                Input::InjectedKey key;
                key.depth = hold[name].asFloat();
                key.steps_left = steps;
                input->injected_keys_[name] = key;
            }
        }

        const Json::Value& look = req["look"];

        if (look.isArray() && look.size() == 2) {
            input->injected_look_[0] += look[0].asFloat();
            input->injected_look_[1] += look[1].asFloat();
        }

        out["held"] = (int)input->injected_keys_.size();
    } else if (cmd == "load_level") {
        engine->ScriptableUICallback(req.get("path", "").asString());
    } else if (cmd == "host") {
        // Host first, then load: hosting inside a loaded level hands the
        // host's own character to the first client (Phase 0 finding)
        if (req.isMember("port")) {
            config.GetRef("mp_port") = req["port"].asInt();
        }

        online->StartHostingMultiplayer();
        std::string level = req.get("level", "").asString();

        if (req.isMember("campaign")) {
            engine->ScriptableUICallback("set_campaign " + req["campaign"].asString());  // its script advances levels
        }
        if (!level.empty()) {
            engine->ScriptableUICallback(level);
        }
    } else if (cmd == "connect") {
        if (req.isMember("port")) {
            config.GetRef("mp_port") = req["port"].asInt();
        }

        online->ConnectByIPAddress(req.get("ip", "127.0.0.1").asString());
        out["active"] = online->IsActive();
    } else if (cmd == "disconnect") {
        online->QueueStopMultiplayer();
    } else if (cmd == "start_session") {
        // What the host's "press any key" after loading does
        bool ready = !online->IsActive() || !online->IsHosting() || online->AllClientsReady();

        if (ready) {
            online->SessionStarted(true);
            engine->waiting_for_input_ = false;
        }

        out["started"] = ready;
    } else if (cmd == "perf") {
        // Overgrowth Online: the frame timers (Graphics/frame_timers.h): {"enable": bool},
        // and the last one-second window, every zone's GPU and CPU time per frame
        if (req.isMember("enable")) {
            FrameTimers::SetEnabled(req["enable"].asBool());
        }
        FrameTimers::Snapshot snap = FrameTimers::Last();
        out["on"] = FrameTimers::g_on;
        out["window_s"] = snap.window_s;
        out["frames"] = snap.frames;
        out["frame_ms"] = snap.frame_ms;
        out["frame_ms_max"] = snap.frame_ms_max;
        Json::Value zones(Json::arrayValue);
        for (const FrameTimers::Zone& z : snap.zones) {
            Json::Value v;
            v["path"] = z.path;
            v["depth"] = z.depth;
            v["gpu_ms"] = z.gpu_ms;
            v["cpu_ms"] = z.cpu_ms;
            v["calls"] = z.calls;
            zones.append(v);
        }
        out["zones"] = zones;
    } else if (cmd == "view") {
        // Overgrowth Online: a fixed view for screenshot comparisons: {"pos": [x, y, z],
        // "target": [x, y, z], "fov": degrees} holds the camera there with the game
        // paused; {"clear": true} lets go
        if (req.get("clear", false).asBool()) {
            engine->view_override_ = false;
            engine->user_paused = false;
            game_timer.render_time_frozen = false;
        } else {
            const Json::Value& p = req["pos"];
            const Json::Value& t = req["target"];
            if (!p.isArray() || p.size() != 3 || !t.isArray() || t.size() != 3) {
                throw std::runtime_error("view: pos and target are [x, y, z]");
            }
            engine->view_override_pos_ = vec3(p[0].asFloat(), p[1].asFloat(), p[2].asFloat());
            engine->view_override_target_ = vec3(t[0].asFloat(), t[1].asFloat(), t[2].asFloat());
            engine->view_override_fov_ = req.get("fov", 0.0).asFloat();
            engine->view_override_gui_ = req.get("gui", false).asBool();
            engine->view_override_ = true;
            engine->user_paused = req.get("pause", true).asBool();
            // Shaders' time held at a value of the test's (grass, sky, water the same every run)
            game_timer.frozen_render_time = req.get("time", 100.0).asFloat();
            game_timer.render_time_frozen = true;
        }
        out["view"] = engine->view_override_;
    } else if (cmd == "gl_problems") {
        // Overgrowth Online: the GL debug output's problems, each message with its count
        if (req.get("provoke", false).asBool()) {
            glEnable(0x7FFF);  // an invalid enum on purpose: the output must report it (gl_errors)
        }
        out["alive"] = GLDebugOutputAlive();  // the output reached the callback (our check marker)
        out["count"] = GLDebugProblemCount();
        Json::Value list(Json::arrayValue);
        for (const auto& problem : GLDebugProblems()) {
            Json::Value v;
            v["message"] = problem.first;
            v["count"] = problem.second;
            list.append(v);
        }
        out["problems"] = list;
    } else if (cmd == "screenshot") {
        Graphics* g = Graphics::Instance();
        g->queued_screenshot_path = req.get("path", "").asString();
        g->screenshot_mode = Graphics::kGameplay;
        g->pre_screenshot_media_mode_state = g->media_mode();
        g->queued_screenshot = 1;
    } else if (cmd == "menu") {
        // A menu script, as a menu button opens it ("multiplayer_menu.as")
        Engine::Instance()->ScriptableUICallback(req.get("script", "").asString());
    } else if (cmd == "net_sim") {
        online->net_sim_latency_ms = req.get("latency_ms", 0).asInt();
        online->net_sim_jitter_ms = req.get("jitter_ms", 0).asInt();
        online->net_sim_loss = req.get("loss", 0.0).asFloat();
    } else if (cmd == "spawn") {
        // Host (or offline): a character from an object file, replicated to
        // the clients the way the engine adds a hot-joining player
        if (!loaded || online->IsClient()) {
            throw std::runtime_error("spawn needs the host");
        }
        std::string path = req.get("path", "Data/Objects/IGF_Characters/IGF_GuardActor.xml").asString();
        const Json::Value& p = req["pos"];
        vec3 pos(p[0].asFloat(), p[1].asFloat(), p[2].asFloat());
        int id = sg->map_editor->CreateObject(path);
        Object* obj = sg->GetObjectFromID(id);
        if (obj == nullptr) {
            throw std::runtime_error("spawn failed: " + path);
        }
        obj->SetTranslation(pos);
        MovementObject* mo = FindCharacter(sg, id);
        if (mo != nullptr && req.isMember("teams")) {
            mo->GetScriptParams()->ASSetString("Teams", req["teams"].asString());
        }
        if (online->IsActive()) {
            online->Send<OnlineMessages::CreateEntity>(path, pos, id);
        }
        out["object_id"] = id;  // ("id" is the request's)
    } else if (cmd == "rig_check") {
        // How well each bone's joint with its parent predicts it right now
        // (the snapshot rig, Online/char_snapshot.h), in mm
        MovementObject* mo = loaded ? FindCharacter(sg, req.get("char", -1).asInt()) : nullptr;
        if (mo == nullptr) {
            throw std::runtime_error("no such character");
        }
        RiggedObject* rig_obj = mo->rigged_object();
        const CharSnapshot::Rig& rig = rig_obj->NetworkRig();
        const std::vector<PhysicsBone>& pb = rig_obj->skeleton_.physics_bones;
        out["bones"] = (int)pb.size();
        out["parents"] = (int)rig_obj->skeleton_.parents.size();
        out["points"] = (int)rig_obj->skeleton_.points.size();
        out["errors_mm"] = Json::Value(Json::arrayValue);
        int linked = 0;

        for (size_t i = 0; i < pb.size() && i < rig.links.size(); ++i) {
            const CharSnapshot::BoneLink& link = rig.links[i];
            if (link.parent < 0 || pb[i].bullet_object == nullptr || pb[link.parent].bullet_object == nullptr) {
                continue;
            }
            ++linked;
            const mat4& tp = pb[link.parent].bullet_object->transform;
            const mat4& tc = pb[i].bullet_object->transform;
            vec3 predicted = CharSnapshot::PredictFromParent(link, QuaternionFromMat4(tp.GetRotationPart()), tp.GetTranslationPart(),
                                                             QuaternionFromMat4(tc.GetRotationPart()));
            Json::Value e(Json::arrayValue);
            e.append((int)i);
            e.append(link.parent);
            e.append(distance(predicted, tc.GetTranslationPart()) * 1000.0f);
            e.append(length(tc.GetTranslationPart() - tp.GetTranslationPart()) * 1000.0f);
            e.append(length(link.joint_in_parent - link.joint_in_child) * 1000.0f);
            out["errors_mm"].append(e);
        }
        out["linked"] = linked;
        out["ends"] = Json::Value(Json::arrayValue);
        for (const PhysicsBone& b : pb) {
            Json::Value e(Json::arrayValue);
            e.append(b.bone);
            if (b.bone >= 0 && b.bone < (int)rig_obj->skeleton_.bones.size()) {
                e.append(rig_obj->skeleton_.bones[b.bone].points[0]);
                e.append(rig_obj->skeleton_.bones[b.bone].points[1]);
            }
            e.append(rig_obj->skeleton_.parents.size() > (size_t)b.bone ? rig_obj->skeleton_.parents[b.bone] : -2);
            e.append(b.bullet_object != nullptr);
            out["ends"].append(e);
        }
    } else if (cmd == "nan_scan") {
        // Objects with a non-finite transform; characters with non-finite bones
        auto bad3 = [](const vec3& v) { return !std::isfinite(v[0]) || !std::isfinite(v[1]) || !std::isfinite(v[2]); };
        auto bad_mats = [](const std::vector<mat4>& mats) {
            for (const mat4& m : mats) {
                for (int i = 0; i < 16; ++i) {
                    if (!std::isfinite(m.entries[i])) {
                        return true;
                    }
                }
            }
            return false;
        };
        out["objects"] = Json::Value(Json::arrayValue);
        out["characters"] = Json::Value(Json::arrayValue);
        if (loaded) {
            for (Object* obj : sg->objects_) {
                if (bad3(obj->GetTranslation()) || bad3(obj->GetScale())) {
                    Json::Value e;
                    e["id"] = obj->GetID();
                    e["type"] = (int)obj->GetType();
                    e["path"] = obj->obj_file;
                    out["objects"].append(e);
                }
            }
            for (Object* obj : sg->movement_objects_) {
                MovementObject* mo = (MovementObject*)obj;
                RiggedObject* r = mo->rigged_object();
                bool pos = bad3(mo->position);
                bool disp = bad_mats(r->display_bone_matrices);
                bool net = bad_mats(r->network_display_bone_matrices);
                bool phys = false;
                for (const PhysicsBone& b : r->skeleton_.physics_bones) {
                    phys = phys || (b.bullet_object != nullptr && bad3(b.bullet_object->transform.GetTranslationPart()));
                }
                bool points = false;
                for (const vec3& p : r->skeleton_.points) {
                    points = points || bad3(p);
                }
                if (pos || disp || net || phys || points) {
                    Json::Value e;
                    e["id"] = mo->GetID();
                    e["proxy"] = mo->network_proxy;
                    e["rig_proxy"] = r->network_proxy;
                    e["position"] = pos;
                    e["display"] = disp;
                    e["network"] = net;
                    e["physics"] = phys;
                    e["points"] = points;
                    e["display_n"] = (int)r->display_bone_matrices.size();
                    e["network_n"] = (int)r->network_display_bone_matrices.size();
                    // which physics bones, and the rig links that rebuild them
                    const CharSnapshot::Rig& rig = r->network_rig;
                    e["nan_bones"] = Json::Value(Json::arrayValue);
                    for (size_t i = 0; i < r->skeleton_.physics_bones.size(); ++i) {
                        const BulletObject* b_o = r->skeleton_.physics_bones[i].bullet_object;
                        if (b_o != nullptr && bad3(b_o->transform.GetTranslationPart())) {
                            Json::Value nb(Json::arrayValue);
                            nb.append((int)i);
                            bool has_link = i < rig.links.size();
                            nb.append(has_link ? rig.links[i].parent : -9);
                            nb.append(has_link && (bad3(rig.links[i].joint_in_parent) || bad3(rig.links[i].joint_in_child)));
                            nb.append(bad3(r->skeleton_.physics_bones[i].initial_position));
                            e["nan_bones"].append(nb);
                        }
                    }
                    e["rig_scale"] = r->network_rig_scale;
                    e["model_scale"] = r->GetCharScale();
                    out["characters"].append(e);
                }
            }
        }
    } else if (cmd == "level_messages") {
        // Level messages seen since the last call (filter by prefix)
        std::string prefix = req.get("prefix", "").asString();
        out["messages"] = Json::Value(Json::arrayValue);
        for (const std::string& msg : level_messages_) {
            if (msg.compare(0, prefix.size(), prefix) == 0) {
                out["messages"].append(msg);
            }
        }
        level_messages_.clear();
    } else if (cmd == "track") {
        // Record these characters every step (clears the previous recording)
        online->track_samples.clear();
        for (const Json::Value& id : req["ids"]) {
            online->track_samples[id.asInt()];
        }
    } else if (cmd == "track_get") {
        Json::Value& t = out["tracks"];
        t = Json::Value(Json::objectValue);

        for (const auto& it : online->track_samples) {
            Json::Value rows(Json::arrayValue);
            for (const Online::TrackSample& s : it.second) {
                Json::Value row(Json::arrayValue);
                row.append(s.clock_ms);
                row.append(s.render_src_ms);
                for (int i = 0; i < 3; ++i) {
                    row.append(s.position[i]);
                }
                for (int i = 0; i < 3; ++i) {
                    row.append(s.bones[i]);
                }
                rows.append(row);
            }
            t[std::to_string(it.first)] = rows;
        }

        if (req.get("stop", false).asBool()) {
            online->track_samples.clear();
        }
    } else if (cmd == "quit") {
        engine->quitting_ = true;
    } else {
        throw std::runtime_error("unknown command: " + cmd);
    }
}
